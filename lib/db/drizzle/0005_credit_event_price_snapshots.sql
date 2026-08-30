ALTER TABLE "kredit_events" ADD COLUMN "occurred_at" timestamp with time zone;--> statement-breakpoint
ALTER TABLE "kredit_events" ADD COLUMN "price_revision_id" integer;--> statement-breakpoint
ALTER TABLE "kredit_events" ADD COLUMN "unit_price_cents" integer;--> statement-breakpoint
ALTER TABLE "kredit_events" ADD CONSTRAINT "kredit_events_price_revision_id_product_price_revisions_id_fk" FOREIGN KEY ("price_revision_id") REFERENCES "public"."product_price_revisions"("id") ON DELETE restrict ON UPDATE no action;